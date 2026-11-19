#pragma once
#include "sleep_steam_tracking.h"
#include <utility>
#include <array>
namespace ymcc::sleepsteam {
// Bounded discovery ONLY. A row never proves a process is alive, is a game,
// belongs to our session, or is the original. Native birth/path/window guards
// must prove those separately before a PRE-sleep target can be pinned.
struct TrackedProcessRow {
    std::uint32_t appid=0,pid=0;std::string path,at;
    // Exact log spelling, not a synthesized shortcut or an action proof.
    // Keep the original first four aggregate fields and normalized map key.
    std::string gameIdRaw{};
    std::uint64_t fullGameId=0;
    bool fullGameIdKnown=false;
    unsigned sourceLine=0;
    // A normalized-key collision cannot be repaired by a later add row.
    // This metadata follows the original drop/remove/client-reset lifetime.
    bool identityConflict=false;
};
struct TrackedProcessInventory {bool complete=true;std::string reason;std::vector<TrackedProcessRow> rows;};
inline TrackedProcessInventory trackedProcessInventory(const std::vector<std::string>& lines,
    TrackingScope scope=TrackingScope::RunningListAndProcess) {
    TrackedProcessInventory result;std::map<std::pair<std::uint32_t,std::uint32_t>,TrackedProcessRow> live;
    if(!trackerLinesWithinBudget(lines)){result.complete=false;result.reason="steam-tracker-byte-limit";return result;}
    unsigned scanned=0;
    for(const auto& line:lines){
        ++scanned;std::string at;if(!timestamp(line,at))continue;const auto text=line.substr(22);
        if(text.rfind("Client version:",0)==0){live.clear();continue;}
        if(text.rfind("Remove ",0)==0){
            const auto end=text.find(" from running list",7);std::uint32_t app=0;
            if(scope==TrackingScope::RunningListAndProcess&&end!=std::string::npos&&appId(text.substr(7,end-7),app))
                for(auto i=live.begin();i!=live.end();)if(i->first.first==app)i=live.erase(i);else ++i;
            continue;
        }
        if(text.rfind("AppID ",0)!=0)continue;
        const auto sep=text.find(' ',6);std::uint32_t app=0;
        if(sep==std::string::npos||!appId(text.substr(6,sep-6),app))continue;
        const auto action=text.substr(sep);const std::string adding=" adding PID ",dropping=" no longer tracking PID ";
        const bool add=action.rfind(adding,0)==0,drop=action.rfind(dropping,0)==0;if(!add&&!drop)continue;
        const auto begin=add?adding.size():dropping.size(),end=action.find_first_of(" ,",begin);std::uint64_t pid=0;
        if(end==std::string::npos||!unsignedDecimal(action.substr(begin,end-begin),pid)||!pid||pid>0xffffffffULL)continue;
        const auto key=std::make_pair(app,static_cast<std::uint32_t>(pid));
        if(drop){live.erase(key);continue;}
        const std::string marker=" as a tracked process ";if(action.compare(end,marker.size(),marker)!=0)continue;
        const auto path=commandExe(action.substr(end+marker.size()));if(path.empty())continue;
        TrackedProcessRow row{app,static_cast<std::uint32_t>(pid),path,at};
        row.gameIdRaw=text.substr(6,sep-6);row.sourceLine=scanned;
        std::uint64_t full=0;
        // Native AppID and complete type-2 GameID are distinct known shapes.
        // Negative aliases and shortcut short IDs stay unknown; never expand.
        row.fullGameIdKnown=unsignedDecimal(row.gameIdRaw,full)&&
            ((app<=0xffffffU&&full==app)||
             ((full>>32)==app&&(full&0xffffffffULL)==0x02000000ULL));
        if(row.fullGameIdKnown)row.fullGameId=full;
        const auto previous=live.find(key);
        if(previous!=live.end()){
            const auto& old=previous->second;
            row.identityConflict=old.identityConflict||
                old.fullGameIdKnown!=row.fullGameIdKnown||
                (old.fullGameIdKnown&&old.fullGameId!=row.fullGameId)||
                (!old.fullGameIdKnown&&old.gameIdRaw!=row.gameIdRaw)||
                pathKey(old.path)!=pathKey(row.path);
        }
        live[key]=std::move(row);
        if(live.size()>32){result.complete=false;result.reason="steam-tracker-live-process-limit";return result;}
    }
    for(const auto& item:live)result.rows.push_back(item.second);
    return result;
}

// Index 0 is RunningListAndProcess; index 1 is ProcessLifetime. The original
// single-view parser remains unchanged. A view that hits its member cap stays
// failed even when later rows reset or remove entries in the other view.
inline std::array<TrackedProcessInventory,2> trackedProcessInventories(const std::vector<std::string>& lines) {
    std::array<TrackedProcessInventory,2> result;
    if(lines.empty())return result;
    if(!trackerLinesWithinBudget(lines)){
        for(auto& view:result){view.complete=false;view.reason="steam-tracker-byte-limit";}
        return result;
    }
    std::array<std::map<std::pair<std::uint32_t,std::uint32_t>,TrackedProcessRow>,2> live;
    unsigned scanned=0;
    for(const auto& line:lines){
        ++scanned;std::string at;if(!timestamp(line,at))continue;const auto text=line.substr(22);
        if(text.rfind("Client version:",0)==0){
            for(std::size_t i=0;i<2;++i)if(result[i].complete)live[i].clear();
            continue;
        }
        if(text.rfind("Remove ",0)==0){
            const auto end=text.find(" from running list",7);std::uint32_t app=0;
            if(result[0].complete&&end!=std::string::npos&&appId(text.substr(7,end-7),app))
                for(auto i=live[0].begin();i!=live[0].end();)if(i->first.first==app)i=live[0].erase(i);else ++i;
            continue;
        }
        if(text.rfind("AppID ",0)!=0)continue;
        const auto sep=text.find(' ',6);std::uint32_t app=0;
        if(sep==std::string::npos||!appId(text.substr(6,sep-6),app))continue;
        const auto action=text.substr(sep);const std::string adding=" adding PID ",dropping=" no longer tracking PID ";
        const bool add=action.rfind(adding,0)==0,drop=action.rfind(dropping,0)==0;if(!add&&!drop)continue;
        const auto begin=add?adding.size():dropping.size(),end=action.find_first_of(" ,",begin);std::uint64_t pid=0;
        if(end==std::string::npos||!unsignedDecimal(action.substr(begin,end-begin),pid)||!pid||pid>0xffffffffULL)continue;
        const auto key=std::make_pair(app,static_cast<std::uint32_t>(pid));
        if(drop){for(std::size_t i=0;i<2;++i)if(result[i].complete)live[i].erase(key);continue;}
        const std::string marker=" as a tracked process ";if(action.compare(end,marker.size(),marker)!=0)continue;
        const auto path=commandExe(action.substr(end+marker.size()));if(path.empty())continue;
        TrackedProcessRow decoded{app,static_cast<std::uint32_t>(pid),path,at};
        decoded.gameIdRaw=text.substr(6,sep-6);decoded.sourceLine=scanned;
        std::uint64_t full=0;
        decoded.fullGameIdKnown=unsignedDecimal(decoded.gameIdRaw,full)&&
            ((app<=0xffffffU&&full==app)||
             ((full>>32)==app&&(full&0xffffffffULL)==0x02000000ULL));
        if(decoded.fullGameIdKnown)decoded.fullGameId=full;
        for(std::size_t i=0;i<2;++i){
            if(!result[i].complete)continue;
            auto row=decoded;
            const auto previous=live[i].find(key);
            if(previous!=live[i].end()){
                const auto& old=previous->second;
                row.identityConflict=old.identityConflict||
                    old.fullGameIdKnown!=row.fullGameIdKnown||
                    (old.fullGameIdKnown&&old.fullGameId!=row.fullGameId)||
                    (!old.fullGameIdKnown&&old.gameIdRaw!=row.gameIdRaw)||
                    pathKey(old.path)!=pathKey(row.path);
            }
            live[i][key]=std::move(row);
            if(live[i].size()>32){
                result[i].complete=false;result[i].reason="steam-tracker-live-process-limit";
                live[i].clear();
            }
        }
        if(!result[0].complete&&!result[1].complete)return result;
    }
    for(std::size_t i=0;i<2;++i)if(result[i].complete)
        for(const auto& item:live[i])result[i].rows.push_back(item.second);
    return result;
}

struct TrackedSourceScopes {
    // Index 0 is RunningListAndProcess; index 1 is ProcessLifetime.
    std::array<Binding,2> bindings;
    std::array<TrackedProcessInventory,2> inventories;
};
// Used only when this call needs all four views of the same immutable source.
// The standalone and dual-only entry points above retain their original paths.
inline TrackedSourceScopes trackedSourceScopes(const std::vector<std::string>& lines,
    std::uint32_t selectedPid,const std::string& selectedPath,const std::string& earliest,const std::string& latest) {
    TrackedSourceScopes result;
    const bool bind=selectedPid&&!selectedPath.empty()&&!earliest.empty()&&!latest.empty()&&earliest<=latest;
    if(lines.empty())return result;
    if(!trackerLinesWithinBudget(lines)){
        for(auto& view:result.inventories){view.complete=false;view.reason="steam-tracker-byte-limit";}
        if(bind)for(auto& view:result.bindings)view.status="incomplete";
        return result;
    }
    std::array<std::map<std::uint32_t,Binding>,2> bindings;
    std::array<bool,2> removed{};
    std::array<std::map<std::pair<std::uint32_t,std::uint32_t>,TrackedProcessRow>,2> live;
    unsigned scanned=0;
    for(const auto& line:lines){
        ++scanned;std::string at;if(!timestamp(line,at))continue;const auto text=line.substr(22);
        if(text.rfind("Client version:",0)==0){
            for(std::size_t i=0;i<2;++i){
                if(result.inventories[i].complete)live[i].clear();
                if(bind)bindings[i].clear();
            }
            removed.fill(false);continue;
        }
        if(text.rfind("Remove ",0)==0){
            const auto end=text.find(" from running list",7);std::uint32_t app=0;
            if(end!=std::string::npos&&appId(text.substr(7,end-7),app)){
                if(result.inventories[0].complete)
                    for(auto i=live[0].begin();i!=live[0].end();)if(i->first.first==app)i=live[0].erase(i);else ++i;
                if(bind&&bindings[0].erase(app))removed[0]=true;
            }
            continue;
        }
        if(text.rfind("AppID ",0)!=0)continue;
        const auto sep=text.find(' ',6);std::uint32_t app=0;
        if(sep==std::string::npos||!appId(text.substr(6,sep-6),app))continue;
        const auto action=text.substr(sep);const std::string adding=" adding PID ",dropping=" no longer tracking PID ";
        const bool add=action.rfind(adding,0)==0,drop=action.rfind(dropping,0)==0;if(!add&&!drop)continue;
        const auto begin=add?adding.size():dropping.size(),end=action.find_first_of(" ,",begin);std::uint64_t pid=0;
        if(end==std::string::npos||!unsignedDecimal(action.substr(begin,end-begin),pid)||!pid||pid>0xffffffffULL)continue;
        const auto key=std::make_pair(app,static_cast<std::uint32_t>(pid));
        if(drop){
            for(std::size_t i=0;i<2;++i){
                if(result.inventories[i].complete)live[i].erase(key);
                if(bind&&pid==selectedPid&&bindings[i].erase(app))removed[i]=true;
            }
            continue;
        }
        const std::string marker=" as a tracked process ";if(action.compare(end,marker.size(),marker)!=0)continue;
        const bool needsInventory=result.inventories[0].complete||result.inventories[1].complete;
        const bool needsBinding=bind&&pid==selectedPid&&at>=earliest&&at<=latest;
        if(!needsInventory&&!needsBinding)continue;
        const auto path=commandExe(action.substr(end+marker.size()));
        if(needsBinding&&pathKey(path)==pathKey(selectedPath)){
            Binding row;row.status="tracked";row.at=at;row.appid=app;row.line=scanned;row.exactPath=true;
            for(auto& state:bindings)state[app]=row;
        }
        // Empty commands cannot create an inventory member. Binding keeps its
        // own original pathKey equality above (including unusual caller paths).
        if(!needsInventory||path.empty())continue;
        TrackedProcessRow decoded{app,static_cast<std::uint32_t>(pid),path,at};
        decoded.gameIdRaw=text.substr(6,sep-6);decoded.sourceLine=scanned;
        std::uint64_t full=0;
        decoded.fullGameIdKnown=unsignedDecimal(decoded.gameIdRaw,full)&&
            ((app<=0xffffffU&&full==app)||((full>>32)==app&&(full&0xffffffffULL)==0x02000000ULL));
        if(decoded.fullGameIdKnown)decoded.fullGameId=full;
        for(std::size_t i=0;i<2;++i){
            if(!result.inventories[i].complete)continue;
            auto row=decoded;const auto previous=live[i].find(key);
            if(previous!=live[i].end()){
                const auto& old=previous->second;
                row.identityConflict=old.identityConflict||old.fullGameIdKnown!=row.fullGameIdKnown||
                    (old.fullGameIdKnown&&old.fullGameId!=row.fullGameId)||
                    (!old.fullGameIdKnown&&old.gameIdRaw!=row.gameIdRaw)||pathKey(old.path)!=pathKey(row.path);
            }
            live[i][key]=std::move(row);
            if(live[i].size()>32){result.inventories[i].complete=false;
                result.inventories[i].reason="steam-tracker-live-process-limit";live[i].clear();}
        }
        if(!bind&&!result.inventories[0].complete&&!result.inventories[1].complete)return result;
    }
    for(std::size_t i=0;i<2;++i){
        if(bind){
            if(bindings[i].size()==1)result.bindings[i]=bindings[i].begin()->second;
            else result.bindings[i].status=bindings[i].empty()?(removed[i]?"removed":"unknown"):"ambiguous";
        }
        if(result.inventories[i].complete)
            for(const auto& item:live[i])result.inventories[i].rows.push_back(item.second);
    }
    return result;
}
}
