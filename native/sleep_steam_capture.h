#pragma once
#include "sleep_steam_tracking.h"
namespace ymcc::sleepsteam {
// Bounded discovery ONLY. A row never proves a process is alive, is a game,
// belongs to our session, or is the original. Native birth/path/window guards
// must prove those separately before a PRE-sleep target can be pinned.
struct TrackedProcessRow {std::uint32_t appid=0,pid=0;std::string path,at;};
struct TrackedProcessInventory {bool complete=true;std::string reason;std::vector<TrackedProcessRow> rows;};
inline TrackedProcessInventory trackedProcessInventory(const std::vector<std::string>& lines,
    TrackingScope scope=TrackingScope::RunningListAndProcess) {
    TrackedProcessInventory result;std::map<std::pair<std::uint32_t,std::uint32_t>,TrackedProcessRow> live;
    if(!trackerLinesWithinBudget(lines)){result.complete=false;result.reason="steam-tracker-byte-limit";return result;}
    for(const auto& line:lines){
        std::string at;if(!timestamp(line,at))continue;const auto text=line.substr(22);
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
        live[key]={app,static_cast<std::uint32_t>(pid),path,at};
        if(live.size()>32){result.complete=false;result.reason="steam-tracker-live-process-limit";return result;}
    }
    for(const auto& item:live)result.rows.push_back(item.second);
    return result;
}
}
